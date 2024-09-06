import React, { useEffect, useState } from 'react'
import PrintingA4 from './PrintingA4'
import { Stack, Typography } from '@mui/material'
import QRCode from "react-qr-code";
import { useParams } from 'react-router-dom';
import { useDispatch } from 'react-redux';
import { doGetRequest } from '../../Common/StaticFunctionsTyped';

type Props = {}

const PrintQR = (props: Props) => {
    const params = useParams()
    const dispatch = useDispatch()
    const [qr, setqr] = useState("")

    useEffect(() => {
        doGetRequest(`${params.organization}/${params.date}/qr`, dispatch).then((response) => {
            setqr(`${window.location.origin}/u/${params.organization}/${params.date}/${response.content}`)
        })
    }, [params.date, params.organization, dispatch])

    return (
        <PrintingA4 generationType='QR-Code'>
            <Stack alignItems="center" gap={5}>
                <Typography variant="h3">Hier gehts zu den Live-Ergebnissen</Typography>

                <QRCode value={qr} />
                <Typography variant="h5">Scan den QR-Code oder gehe auf die folgende URL:</Typography>
                <Typography >{qr}</Typography>
            </Stack>
        </PrintingA4>
    )
}

export default PrintQR